(function(){
  const summary = document.getElementById('transport-json');
  const form = document.getElementById('task-form');
  const input = document.getElementById('task-input');
  const list = document.getElementById('task-list');
  const storageKey = 'syphax-web-example-tasks';

  function loadTasks(){
    try {
      return JSON.parse(localStorage.getItem(storageKey)) || [
        'Check certificate paths',
        'Review static assets',
        'Open API summary'
      ];
    } catch (_) {
      return [];
    }
  }

  function saveTasks(tasks){
    localStorage.setItem(storageKey, JSON.stringify(tasks));
  }

  function renderTasks(tasks){
    list.textContent = '';
    tasks.forEach(function(task, index){
      const item = document.createElement('li');
      const label = document.createElement('span');
      const remove = document.createElement('button');

      label.textContent = task;
      remove.type = 'button';
      remove.textContent = 'Remove';
      remove.addEventListener('click', function(){
        const next = loadTasks();
        next.splice(index, 1);
        saveTasks(next);
        renderTasks(next);
      });
      item.append(label, remove);
      list.append(item);
    });
  }

  fetch('/api/summary')
    .then(function(response){ return response.json(); })
    .then(function(data){ summary.textContent = JSON.stringify(data, null, 2); })
    .catch(function(error){ summary.textContent = String(error); });

  form.addEventListener('submit', function(event){
    event.preventDefault();
    const value = input.value.trim();
    const tasks = loadTasks();

    if (!value) {
      input.focus();
      return;
    }
    tasks.push(value);
    saveTasks(tasks);
    input.value = '';
    renderTasks(tasks);
  });

  renderTasks(loadTasks());
})();
